/* SPDX-License-Identifier: BSD-3-Clause
 * Private per-device boot cache, not a calibration format decoder. */
#ifndef SPX_FSG_RECORD_H
#define SPX_FSG_RECORD_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define SPX_FSG_HEADER 4096u
#define SPX_FSG_CAPACITY (2u*1024u*1024u)
static uint32_t fsg_u32(const uint8_t *p) {
 return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static inline void fsg_put32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i)); }
static uint32_t fsg_crc(const uint8_t *p,size_t n,int header) {
 uint32_t c=~0u;
 for(size_t i=0;i<n;i++){c^=(header && i>=20 && i<24)?0:p[i];for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&(0u-(c&1)));}
 return ~c;
}
static int fsg_record_valid(const uint8_t *p,size_t n,const char *uuid) {
 if(n<SPX_FSG_HEADER || memcmp(p,"SPXFSG02",8))return 0;
 uint32_t bytes=fsg_u32(p+8);
 return bytes && bytes<=SPX_FSG_CAPACITY && n==SPX_FSG_HEADER+(size_t)bytes &&
  strlen(uuid)==36 && !memcmp(p+32,uuid,36) &&
  fsg_crc(p,128,1)==fsg_u32(p+20) && fsg_crc(p+SPX_FSG_HEADER,bytes,0)==fsg_u32(p+16);
}
#endif
