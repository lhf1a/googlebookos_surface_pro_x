/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SQ2_IRIS1_HFI_H
#define SQ2_IRIS1_HFI_H
struct device;
struct sq2_hfi_transport;
int sq2_hfi_prepare(struct device *dev, struct sq2_hfi_transport **out);
int sq2_hfi_run(struct sq2_hfi_transport *ctx, void __iomem *base, bool protect_iova);
int sq2_hfi_finish(struct sq2_hfi_transport *ctx);
void sq2_hfi_quarantine(struct sq2_hfi_transport *ctx);
int sq2_hfi_dma_probe(struct device *dev);
#endif
