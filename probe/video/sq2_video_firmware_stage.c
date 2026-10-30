// SPDX-License-Identifier: GPL-2.0-only
/* Firmware copy/readback only. No SCM calls, register writes, or VPU start. */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/dma-direct.h>
#include <linux/iommu.h>
#include <linux/of.h>
#include <linux/firmware.h>
#include <linux/sizes.h>
#include <linux/soc/qcom/mdt_loader.h>
#include <crypto/hash.h>
#include <crypto/sha2.h>
#include "sq2_video_fw_layout.h"

static bool allow;
module_param(allow, bool, 0400);
static const char fwname[] = "qcom/venus-5.2/venus.mbn";
static const u8 expected_sha256[SHA256_DIGEST_SIZE] = {
 0x4e,0x93,0xd3,0x45,0x04,0xda,0x96,0x32,0x07,0x75,0x47,0x6b,0xbd,0x50,0x9a,0xa0,
 0x6f,0x98,0x5b,0x48,0xfb,0x78,0x1d,0x44,0x36,0xc8,0xeb,0x81,0x2b,0x92,0x99,0x28
};

static int check_digest(const struct firmware *fw)
{
 struct crypto_shash *tfm;
 struct shash_desc *desc;
 u8 digest[SHA256_DIGEST_SIZE];
 int ret;

 tfm = crypto_alloc_shash("sha256", 0, 0);
 if (IS_ERR(tfm))
  return PTR_ERR(tfm);
 desc = kzalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
 if (!desc) {
  ret = -ENOMEM;
  goto out;
 }
 desc->tfm = tfm;
 ret = crypto_shash_digest(desc, fw->data, fw->size, digest);
 if (!ret && memcmp(digest, expected_sha256, sizeof(digest)))
  ret = -EKEYREJECTED;
 kfree(desc);
out:
 crypto_free_shash(tfm);
 return ret;
}

static int __init firmware_stage(void)
{
 struct platform_device *pdev;
 const struct firmware *fw;
 struct sq2_fw_layout layout;
 struct device *dev;
 dma_addr_t dma;
 phys_addr_t phys, reloc = 0;
 unsigned char *cpu;
 u32 i;
 int ret;

 if (!allow || !of_machine_is_compatible("microsoft,surface-pro-x"))
  return -EPERM;
 pdev = platform_device_register_simple("sq2-video-firmware-stage", -1, NULL, 0);
 if (IS_ERR(pdev))
  return PTR_ERR(pdev);
 dev = &pdev->dev;
 if (iommu_get_domain_for_dev(dev) || get_dma_ops(dev) || dev->dma_range_map) {
  ret = -EINVAL;
  goto unregister;
 }
 ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
 if (ret)
  goto unregister;
 ret = request_firmware_direct(&fw, fwname, dev);
 if (ret)
  goto unregister;
 if (fw->size != 1159200 || sq2_fw_layout(fw->data, fw->size, &layout)) {
  ret = -ENOEXEC;
  goto release;
 }
 ret = check_digest(fw);
 if (ret)
  goto release;
 if (qcom_mdt_get_size(fw) != layout.span) {
  ret = -ENOEXEC;
  goto release;
 }
 cpu = dma_alloc_coherent(dev, SQ2_FW_LIMIT, &dma, GFP_KERNEL | __GFP_NOWARN);
 if (!cpu) {
  ret = -ENOMEM;
  goto release;
 }
 phys = dma_to_phys(dev, dma);
 if (phys != dma || !IS_ALIGNED(phys, SZ_1M) ||
     phys + SQ2_FW_LIMIT > (1ULL << 32)) {
  ret = -ERANGE;
  goto free;
 }
 memset(cpu, 0xa5, SQ2_FW_LIMIT);
 ret = qcom_mdt_load_no_init(dev, fw, fwname, 9, cpu, phys, SQ2_FW_LIMIT, &reloc);
 if (ret)
  goto free;
 if (reloc != layout.base) {
  ret = -EIO;
  goto free;
 }
 for (i = 0; i < layout.count; i++) {
  const struct sq2_fw_segment *s = &layout.segments[i];
  unsigned char *dst = cpu + s->address - layout.base;

  if (memcmp(dst, fw->data + s->source, s->filesz) ||
      memchr_inv(dst + s->filesz, 0, s->memsz - s->filesz)) {
   ret = -EIO;
   goto free;
  }
 }
 pr_info("SQ2_VIDEO_STAGE segments=%u span=%u phys=%pa COPY_BSS_VERIFIED; NO_PAS_NO_BOOT\n",
         layout.count, layout.span, &phys);
free:
 /* No secure-world call or device DMA can refer to this allocation. */
 memzero_explicit(cpu, SQ2_FW_LIMIT);
 dma_free_coherent(dev, SQ2_FW_LIMIT, cpu, dma);
release:
 release_firmware(fw);
unregister:
 platform_device_unregister(pdev);
 pr_info("SQ2_VIDEO_STAGE cleanup_complete ret=%d\n", ret);
 return ret;
}
static void __exit firmware_stage_exit(void) { }
module_init(firmware_stage);
module_exit(firmware_stage_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SQ2 fixed video firmware staging/readback only; never starts VPU");
