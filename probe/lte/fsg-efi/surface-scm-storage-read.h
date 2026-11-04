/* SPDX-License-Identifier: BSD-3-Clause */
/* Pure request encoder. NO SMC, EFI, device, I/O, INIT, WRITE or ERASE path.
 * ABI: pinned QcSOCPartition.sys and SCM storage v2 (2026-01-26).
 * This does not authorize arbitrary addresses or attest physical allocation;
 * a future transport must supply owned, mapped, coherent memory and validate
 * its own response convention. Only verified 4096-byte geometry is supported.
 */
#ifndef SURFACE_SCM_STORAGE_READ_H
#define SURFACE_SCM_STORAGE_READ_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define SPX_STORAGE_COMMAND 0x02001a01u
#define SPX_STORAGE_ARGINFO 0x214u
#define SPX_STORAGE_READ 1u
#define SPX_STORAGE_GET_INFO 4u
#define SPX_STORAGE_INFO_BYTES 105u
#define SPX_STORAGE_PAGE 4096u
#define SPX_STORAGE_REQUEST_BYTES 72u
#define SPX_STORAGE_MAX_BLOCKS 262144u /* local 1GiB bound; not measured capacity */

struct spx_storage_geometry { uint64_t blocks; uint32_t block_bytes, page_bytes; };
struct spx_storage_descriptor { uint32_t command, arginfo; uint64_t args[4]; };

static uint32_t spx_storage_u32(const uint8_t *p) {
 return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t spx_storage_u64(const uint8_t *p) {
 return spx_storage_u32(p)|(uint64_t)spx_storage_u32(p+4)<<32;
}
static void spx_storage_put64(uint8_t *p,uint64_t v) {
 for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(8*i));
}
static int spx_storage_parse_info(const uint8_t *p,size_t n,struct spx_storage_geometry *out) {
 if(!p || !out || n!=SPX_STORAGE_INFO_BYTES)return 0;
 uint64_t blocks=spx_storage_u64(p);uint32_t block=spx_storage_u32(p+8),page=spx_storage_u32(p+12);
 /* num_physical at +16 is descriptive metadata, not an LBA bound.
  * The pinned Windows consumer and SCM storage v2 do not require 1..8.
  * Keep raw metadata in diagnostics; never substitute a guessed value. */
 if(blocks<4 || blocks>SPX_STORAGE_MAX_BLOCKS || block!=SPX_STORAGE_PAGE ||
    !page || page>block || (page&(page-1)))return 0;
 *out=(struct spx_storage_geometry){blocks,block,page};return 1;
}
/* metadata page at base; output page at base+4096. cap covers metadata only.
 * Failed validation leaves both outputs unchanged. The caller must pass two
 * separate live output objects, not overlap desc with request.
 */
static int spx_storage_encode(uint8_t *request,size_t cap,struct spx_storage_descriptor *desc,
                             uint64_t base,unsigned operation,uint64_t lba,
                             const struct spx_storage_geometry *geometry) {
 if(!request || !desc || cap<SPX_STORAGE_REQUEST_BYTES || !base ||
    base%SPX_STORAGE_PAGE || base>UINT64_MAX-(2*SPX_STORAGE_PAGE-1))return 0;
 uint64_t length;
 if(operation==SPX_STORAGE_GET_INFO) {
  if(lba || geometry)return 0;
  length=SPX_STORAGE_INFO_BYTES;
 } else if(operation==SPX_STORAGE_READ) {
  if(!geometry || geometry->block_bytes!=SPX_STORAGE_PAGE || geometry->blocks<4 ||
     geometry->blocks>SPX_STORAGE_MAX_BLOCKS || lba>=geometry->blocks)return 0;
  length=SPX_STORAGE_PAGE;
 } else return 0;
 uint8_t tmp[SPX_STORAGE_REQUEST_BYTES]={0};
 spx_storage_put64(tmp,1);spx_storage_put64(tmp+32,operation);
 spx_storage_put64(tmp+40,lba);spx_storage_put64(tmp+48,length);
 spx_storage_put64(tmp+56,base+SPX_STORAGE_PAGE);
 struct spx_storage_descriptor d={SPX_STORAGE_COMMAND,SPX_STORAGE_ARGINFO,{base,40,base+40,32}};
 memcpy(request,tmp,sizeof(tmp));*desc=d;return 1;
}
/* Conservative EFI observation policy, not proof that a particular firmware
 * implements this service. The original driver checks completed=1 and a zero
 * 32-bit service return. Require the whole return slot to be zero here; retain
 * all raw slots in the EFI report rather than silently accepting another ABI.
 * Windows copies the requested byte count and does not require data_size to
 * change. Accept unchanged zero or exactly 105, never a partial/oversize value.
 * All bytes outside the two advertised objects must retain the caller's A5
 * sentinel. This detects bounded overwrites, not arbitrary secure-world bugs.
 */
#define SPX_STORAGE_GETINFO_RESPONSE_V1 1
enum spx_storage_info_check {
 SPX_INFO_OK, SPX_INFO_ARGUMENT, SPX_INFO_RESPONSE, SPX_INFO_REQUEST_CHANGED,
 SPX_INFO_SIZE, SPX_INFO_GUARD, SPX_INFO_GEOMETRY
};
static inline enum spx_storage_info_check spx_storage_check_info(
 const uint8_t *pages,size_t size,const uint8_t expected[64],
 const uint64_t results[4],struct spx_storage_geometry *out) {
 if(!pages || size!=2*SPX_STORAGE_PAGE || !expected || !results || !out)
  return SPX_INFO_ARGUMENT;
 if(results[0]!=1 || results[1]!=0)return SPX_INFO_RESPONSE;
 if(memcmp(pages,expected,64))return SPX_INFO_REQUEST_CHANGED;
 uint64_t returned=spx_storage_u64(pages+64);
 if(returned!=0 && returned!=SPX_STORAGE_INFO_BYTES)return SPX_INFO_SIZE;
 for(size_t i=SPX_STORAGE_REQUEST_BYTES;i<SPX_STORAGE_PAGE;i++)
  if(pages[i]!=0xa5)return SPX_INFO_GUARD;
 for(size_t i=SPX_STORAGE_PAGE+SPX_STORAGE_INFO_BYTES;i<size;i++)
  if(pages[i]!=0xa5)return SPX_INFO_GUARD;
 if(!spx_storage_parse_info(pages+SPX_STORAGE_PAGE,SPX_STORAGE_INFO_BYTES,out))
  return SPX_INFO_GEOMETRY;
 return SPX_INFO_OK;
}
#endif
