// SPDX-License-Identifier: GPL-2.0-only
/* Partial Iris1 backend. No call site is enabled until platform and HFI resource
 * lifetime are adapted. Never use devm for memory potentially owned by PAS9.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/dma-direct.h>
#include <linux/iommu.h>
#include <linux/of.h>
#include <linux/firmware.h>
#include <linux/mutex.h>
#include <linux/sizes.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/soc/qcom/mdt_loader.h>
#include <crypto/hash.h>
#include <crypto/sha2.h>
#include "sq2_video_fw_layout.h"
#include "sq2-fw-transaction.h"
#include "sq2-venus-fw.h"

struct sq2_venus_fw {
 struct sq2_fw_transaction tx;
 struct platform_device *pdev;
 const struct firmware *image; /* only retained during synchronous boot */
 void *cpu;
 dma_addr_t dma;
 phys_addr_t phys;
};
/* This owner and module pin outlive a failed core probe or driver teardown.
 * One PAS9 owner per board; failed shutdown blocks all further boot attempts.
 */
static DEFINE_MUTEX(sq2_fw_lock);
static struct sq2_venus_fw *sq2_fw_owner;
static const char image_name[] = "qcom/venus-5.2/venus.mbn";

static int sq2_fw_check_image(const struct firmware *fw)
{
 static const u8 expected[32] = {
  0x4e,0x93,0xd3,0x45,0x04,0xda,0x96,0x32,0x07,0x75,0x47,0x6b,0xbd,0x50,0x9a,0xa0,
  0x6f,0x98,0x5b,0x48,0xfb,0x78,0x1d,0x44,0x36,0xc8,0xeb,0x81,0x2b,0x92,0x99,0x28
 };
 struct sq2_fw_layout layout;
 struct crypto_shash *tfm;
 struct shash_desc *desc;
 u8 digest[32];
 int ret;

 if (fw->size != 1159200 || sq2_fw_layout(fw->data, fw->size, &layout) ||
     qcom_mdt_get_size(fw) != layout.span)
  return -ENOEXEC;
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
 if (!ret && memcmp(digest, expected, sizeof(digest)))
  ret = -EKEYREJECTED;
 kfree(desc);
out:
 crypto_free_shash(tfm);
 return ret;
}

static int sq2_fw_alloc(void *ctx)
{
 struct sq2_venus_fw *fw = ctx;
 struct device *dev;
 int ret;

 fw->pdev = platform_device_register_simple("sq2-venus-fw-memory", -1, NULL, 0);
 if (IS_ERR(fw->pdev))
  return PTR_ERR(fw->pdev);
 dev = &fw->pdev->dev;
 if (iommu_get_domain_for_dev(dev) || get_dma_ops(dev) || dev->dma_range_map) {
  ret = -EINVAL;
  goto unregister;
 }
 ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
 if (ret)
  goto unregister;
 fw->cpu = dma_alloc_coherent(dev, SQ2_FW_LIMIT, &fw->dma, GFP_KERNEL | __GFP_NOWARN);
 if (!fw->cpu) {
  ret = -ENOMEM;
  goto unregister;
 }
 fw->phys = dma_to_phys(dev, fw->dma);
 if (fw->phys != fw->dma || !IS_ALIGNED(fw->phys, SZ_1M) ||
     fw->phys + SQ2_FW_LIMIT > (1ULL << 32)) {
  ret = -ERANGE;
  goto free;
 }
 memset(fw->cpu, 0, SQ2_FW_LIMIT);
 /* A quarantine must keep the module and auxiliary DMA device alive too. */
 if (!try_module_get(THIS_MODULE)) {
  ret = -ENODEV;
  goto free;
 }
 return 0;
free:
 dma_free_coherent(dev, SQ2_FW_LIMIT, fw->cpu, fw->dma);
 fw->cpu = NULL;
unregister:
 platform_device_unregister(fw->pdev);
 fw->pdev = NULL;
 return ret;
}
static int sq2_fw_load(void *ctx)
{
 struct sq2_venus_fw *fw = ctx;
 return qcom_mdt_load(&fw->pdev->dev, fw->image, image_name, 9,
                      fw->cpu, fw->phys, SQ2_FW_LIMIT, NULL);
}
static int sq2_fw_auth(void *ctx)
{
 return qcom_scm_pas_auth_and_reset(9);
}
static int sq2_fw_pas_stop(void *ctx)
{
 return qcom_scm_pas_shutdown(9);
}
static void sq2_fw_release(void *ctx)
{
 struct sq2_venus_fw *fw = ctx;
 memzero_explicit(fw->cpu, SQ2_FW_LIMIT);
 dma_free_coherent(&fw->pdev->dev, SQ2_FW_LIMIT, fw->cpu, fw->dma);
 platform_device_unregister(fw->pdev);
 fw->cpu = NULL;
 fw->pdev = NULL;
 module_put(THIS_MODULE);
}
static const struct sq2_fw_operations sq2_fw_ops = {
 .alloc = sq2_fw_alloc, .load = sq2_fw_load, .start = sq2_fw_auth,
 .stop = sq2_fw_pas_stop, .release = sq2_fw_release,
};

int sq2_venus_fw_boot(struct device *dev, struct sq2_venus_fw **out)
{
 struct sq2_venus_fw *fw;
 int ret;
 if (!out || *out || !of_machine_is_compatible("microsoft,surface-pro-x"))
  return -EINVAL;
 if (!qcom_scm_is_available())
  return -EPROBE_DEFER;
 if (!qcom_scm_pas_supported(9))
  return -EOPNOTSUPP;
 mutex_lock(&sq2_fw_lock);
 if (sq2_fw_owner) {
  ret = -EBUSY;
  goto unlock;
 }
 fw = kzalloc(sizeof(*fw), GFP_KERNEL);
 if (!fw) {
  ret = -ENOMEM;
  goto unlock;
 }
 ret = request_firmware_direct(&fw->image, image_name, dev);
 if (ret)
  goto free;
 ret = sq2_fw_check_image(fw->image);
 if (!ret)
  ret = sq2_fw_start(&fw->tx, &sq2_fw_ops, fw);
 release_firmware(fw->image);
 fw->image = NULL;
 if (fw->tx.state != SQ2_FW_EMPTY) {
  sq2_fw_owner = fw;
  *out = fw;
  if (fw->tx.state == SQ2_FW_QUARANTINED)
   pr_err("SQ2_VENUS_FW shutdown=%d; DMA memory/device/module pinned until reboot\n",
          fw->tx.stop_error);
  goto unlock;
 }
free:
 kfree(fw);
unlock:
 mutex_unlock(&sq2_fw_lock);
 return ret;
}

int sq2_venus_fw_shutdown(struct sq2_venus_fw **owner)
{
 struct sq2_venus_fw *fw;
 int ret = 0;
 if (!owner)
  return -EINVAL;
 mutex_lock(&sq2_fw_lock);
 fw = *owner;
 if (!fw)
  goto unlock;
 if (fw != sq2_fw_owner) {
  ret = -EINVAL;
  goto unlock;
 }
 ret = sq2_fw_stop(&fw->tx, &sq2_fw_ops, fw);
 if (!ret) {
  sq2_fw_owner = NULL;
  *owner = NULL;
  kfree(fw);
 }
unlock:
 mutex_unlock(&sq2_fw_lock);
 return ret;
}
