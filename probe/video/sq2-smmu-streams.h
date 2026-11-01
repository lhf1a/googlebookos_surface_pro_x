/* SPDX-License-Identifier: GPL-2.0-only */
/* Only architected GR0 stream registers already read by arm-smmu.
 * No context-bank, translation-table, secure alias or register writes.
 */
#ifndef SQ2_SMMU_STREAMS_H
#define SQ2_SMMU_STREAMS_H
#define SQ2_SMMU_STREAMS 128
#define SQ2_SMMU_GR0_BYTES 0x1000
struct sq2_smmu_streams {
 u32 smr[SQ2_SMMU_STREAMS];
 u32 s2cr[SQ2_SMMU_STREAMS];
};
static void sq2_smmu_streams_read(void __iomem *base,
                                 struct sq2_smmu_streams *snapshot)
{
 unsigned int i;
 for (i = 0; i < SQ2_SMMU_STREAMS; i++) {
  snapshot->smr[i] = readl(base + 0x800 + 4 * i);
  snapshot->s2cr[i] = readl(base + 0xc00 + 4 * i);
 }
}
#endif
