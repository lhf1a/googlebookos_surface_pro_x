/* SPDX-License-Identifier: BSD-3-Clause */
/* Bounded primary GPT parsing for own-device FSG collection; no storage writes. */
#ifndef SPX_PRIMARY_GPT_H
#define SPX_PRIMARY_GPT_H
#define SPX_GPT_TABLE_MAX 16384u
struct spx_gpt_plan { uint64_t entries_lba, first, last; uint32_t count, bytes, blocks, crc; };
struct spx_gpt_report {
 struct spx_scm_info_report info;
 EFI_STATUS status, allocation_status, call_status, free_status;
 UINT64 last_lba, results[4], returned_size;
 UINT32 reads, stage, valid, fsg_found;
 UINT64 fsg_first, fsg_blocks;
 struct spx_gpt_plan plan;
 /* Raw private metadata, including GUIDs; never publish in public Git/logs. */
 UINT8 mbr[4096], header[4096], table[SPX_GPT_TABLE_MAX];
};
static uint32_t spx_gpt_crc(const uint8_t *p,size_t n,int header) {
 uint32_t c=~0u;
 for(size_t i=0;i<n;i++){c^=(header && i>=16 && i<20)?0:p[i];for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u & (0u-(c&1)));}
 return ~c;
}
static int spx_gpt_zero(const uint8_t *p,size_t n){for(size_t i=0;i<n;i++)if(p[i])return 0;return 1;}
static int spx_gpt_mbr(const uint8_t *m,const struct spx_storage_geometry *g) {
 if(!m || !g || g->blocks<7 || g->blocks>SPX_STORAGE_MAX_BLOCKS || g->block_bytes!=4096 || m[510]!=0x55 || m[511]!=0xaa)return 0;
 /* OEM protective-MBR length may be all-ones; never use it as capacity. */
 unsigned ee=0;
 for(unsigned i=0;i<4;i++){const uint8_t *e=m+446+16*i;
  if(spx_gpt_zero(e,16))continue;
  if(e[0] || e[4]!=0xee || spx_storage_u32(e+8)!=1 || (spx_storage_u32(e+12)!=g->blocks-1 && spx_storage_u32(e+12)!=UINT32_MAX))return 0;
  ee++;
 }
 return ee==1;
}
static int spx_gpt_header(const uint8_t *h,const struct spx_storage_geometry *g,struct spx_gpt_plan *out) {
 if(!h || !g || !out || g->block_bytes!=4096 || g->blocks<7 || g->blocks>SPX_STORAGE_MAX_BLOCKS)return 0;
 if(memcmp(h,"EFI PART",8) || spx_storage_u32(h+8)!=0x10000 || spx_storage_u32(h+12)!=92 ||
    spx_storage_u32(h+20) || spx_storage_u64(h+24)!=1 || spx_storage_u64(h+32)!=g->blocks-1 ||
    spx_gpt_zero(h+56,16) || spx_gpt_crc(h,92,1)!=spx_storage_u32(h+16))return 0;
 struct spx_gpt_plan p={0};p.first=spx_storage_u64(h+40);p.last=spx_storage_u64(h+48);p.entries_lba=spx_storage_u64(h+72);p.count=spx_storage_u32(h+80);p.crc=spx_storage_u32(h+88);
 if(!p.count || p.count>128 || spx_storage_u32(h+84)!=128)return 0;
 p.bytes=p.count*128;p.blocks=(p.bytes+4095)/4096;
 /* UEFI reserves at least16KiB for each array; no trusting unchecked addition. */
 if(p.first<6 || p.last<p.first || p.last>=g->blocks-5 || p.entries_lba<2 ||
    p.entries_lba>=p.first || p.blocks>p.first-p.entries_lba)return 0;
 *out=p;return 1;
}
static int spx_gpt_entries(const uint8_t *t,const struct spx_gpt_plan *p,uint64_t *first,uint64_t *blocks,unsigned *found) {
 static const uint8_t fsg[16]={0xe2,0xf8,0x8f,0x63,0xc9,0x22,0x3b,0xe3,0x8f,0x5d,0x0e,0x81,0x68,0x6a,0x68,0xcb};
 if(!t || !p || !first || !blocks || !found || !p->count || p->count>128 || p->bytes!=p->count*128 || spx_gpt_crc(t,p->bytes,0)!=p->crc)return 0;
 uint64_t ff=0,nn=0;unsigned hits=0;
 for(unsigned i=0;i<p->count;i++){const uint8_t *e=t+i*128;
  if(spx_gpt_zero(e,16))continue;
  uint64_t a=spx_storage_u64(e+32),b=spx_storage_u64(e+40);
  if(spx_gpt_zero(e+16,16) || a<p->first || b<a || b>p->last)return 0;
  for(unsigned j=0;j<i;j++){const uint8_t *q=t+j*128;if(spx_gpt_zero(q,16))continue;
   if(!memcmp(e+16,q+16,16) || (a<=spx_storage_u64(q+40) && spx_storage_u64(q+32)<=b))return 0;
  }
  if(!memcmp(e,fsg,16)){if(++hits>1)return 0;ff=a;nn=b-a+1;}
 }
 *first=ff;*blocks=nn;*found=hits;return 1;
}
#endif
