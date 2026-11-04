/* SPDX-License-Identifier: BSD-3-Clause */
/* Pure, bounded EFS GPT validation. No device, allocation, or write operations.
 * GUIDs are tied to the pinned qcremotefs8180.sys instruction/data reference.
 * A known GUID identifies a candidate partition, not valid unit calibration.
 */
#ifndef SQ2_LTE_GPT_H
#define SQ2_LTE_GPT_H
#include <stdint.h>
#include <stddef.h>

struct sq2_efs_partition { const char *name; uint64_t start; size_t bytes; };
static const uint8_t sq2_efs_guids[4][16] = {
    {0xaf,0xad,0xbe,0xeb,0xc9,0x22,0x3b,0xe3,0x8f,0x5d,0x0e,0x81,0x68,0x6a,0x68,0xcb},
    {0x1f,0x8b,0x28,0x0a,0xc9,0x22,0x3b,0xe3,0x8f,0x5d,0x0e,0x81,0x68,0x6a,0x68,0xcb},
    {0xe2,0xf8,0x8f,0x63,0xc9,0x22,0x3b,0xe3,0x8f,0x5d,0x0e,0x81,0x68,0x6a,0x68,0xcb},
    {0x16,0x0a,0xb9,0x57,0xc9,0x22,0x3b,0xe3,0x8f,0x5d,0x0e,0x81,0x68,0x6a,0x68,0xcb}
};
static uint32_t sq2_gpt_u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint64_t sq2_gpt_u64(const uint8_t *p) {
    return sq2_gpt_u32(p) | (uint64_t)sq2_gpt_u32(p+4)<<32;
}
/* CRC is checked by the caller. Validate layout before allocating/reading the
 * table. In particular, reject tables that overlap usable data or backup GPT.
 */
static int sq2_gpt_layout(const uint8_t *h, size_t n, uint32_t block,
                          uint64_t disk_last, size_t *table_bytes) {
    static const uint8_t signature[8] = {'E','F','I',' ','P','A','R','T'};
    if (!h || !table_bytes || (block!=512 && block!=4096) || n<92) return 0;
    for (size_t i=0;i<8;i++) if (h[i]!=signature[i]) return 0;
    uint32_t size=sq2_gpt_u32(h+12), count=sq2_gpt_u32(h+80), stride=sq2_gpt_u32(h+84);
    uint64_t backup=sq2_gpt_u64(h+32), first=sq2_gpt_u64(h+40), last=sq2_gpt_u64(h+48);
    uint64_t table=sq2_gpt_u64(h+72);
    if (sq2_gpt_u32(h+8)!=0x00010000 || sq2_gpt_u32(h+20)!=0 ||
        size<92 || size>block || size>n || sq2_gpt_u64(h+24)!=1 ||
        backup!=disk_last || first<3 || first>last || last>=backup ||
        !count || count>1024 || stride<128 || stride>1024 || stride%8 || table<2)
        return 0;
    size_t bytes=(size_t)count*stride;
    uint64_t blocks=(bytes+block-1)/block;
    if (table>=first || blocks>first-table || !blocks) return 0;
    *table_bytes=bytes;
    return 1;
}
static int sq2_efs_partition(const uint8_t *e, size_t n, uint32_t block,
                              uint64_t first, uint64_t last,
                              struct sq2_efs_partition *out) {
    static const char *const names[4] = {"modem_fs1","modem_fs2","modem_fsg","modem_fsc"};
    if (!e || !out || n<128 || (block!=512 && block!=4096) || first>last) return 0;
    int found=-1;
    for (unsigned i=0;i<4;i++) {
        unsigned diff=0;
        for (unsigned j=0;j<16;j++) diff|=e[j]^sq2_efs_guids[i][j];
        if (!diff) found=(int)i;
    }
    if (found<0) return 0;
    uint64_t start=sq2_gpt_u64(e+32), end=sq2_gpt_u64(e+40);
    if (!start || start<first || end<start || end>last || end-start>=8*1024*1024/block)
        return 0;
    out->name=names[found]; out->start=start; out->bytes=(size_t)(end-start+1)*block;
    return 1;
}
#endif
